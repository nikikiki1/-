from datetime import datetime
import uuid
from typing import Optional


class Product:
    def __init__(self, name: str, description: str, price: float, stock_quantity: int, merchant_id: str):
        self._product_id: str = str(uuid.uuid4())
        self._name: str = name
        self._description: str = description
        self._price: float = price
        self._stock_quantity: int = stock_quantity
        self._merchant_id: str = merchant_id
        self._listing_date: datetime = datetime.now()
        self._is_active: bool = True
    
    @property
    def product_id(self) -> str:
        return self._product_id
    
    @property
    def name(self) -> str:
        return self._name
    
    @property
    def description(self) -> str:
        return self._description
    
    @property
    def price(self) -> float:
        return self._price
    
    @property
    def stock_quantity(self) -> int:
        return self._stock_quantity
    
    @property
    def merchant_id(self) -> str:
        return self._merchant_id
    
    @property
    def is_active(self) -> bool:
        return self._is_active
    
    def update_info(self, name: Optional[str] = None, description: Optional[str] = None, price: Optional[float] = None) -> bool:
        """更新商品信息"""
        if name:
            self._name = name
        if description:
            self._description = description
        if price is not None and price >= 0:
            self._price = price
        return True
    
    def decrease_stock(self, quantity: int) -> bool:
        """减少库存"""
        if quantity <= self._stock_quantity:
            self._stock_quantity -= quantity
            # 检查是否需要自动下架
            self.auto_deactivate()
            return True
        return False
    
    def increase_stock(self, quantity: int) -> bool:
        """增加库存"""
        if quantity > 0:
            self._stock_quantity += quantity
            # 如果之前库存为0，现在有库存了，自动上架
            if self._stock_quantity > 0:
                self._is_active = True
            return True
        return False
    
    def auto_deactivate(self) -> bool:
        """当库存为0时自动下架"""
        if self._stock_quantity <= 0:
            self._is_active = False
            return True
        return False
    
    def activate(self) -> bool:
        """手动上架商品"""
        if self._stock_quantity > 0:
            self._is_active = True
            return True
        return False
    
    def deactivate(self) -> bool:
        """手动下架商品"""
        self._is_active = False
        return True
    
    def __str__(self) -> str:
        status = "上架" if self._is_active else "下架"
        return f"商品ID: {self._product_id}\n名称: {self._name}\n价格: ¥{self._price}\n库存: {self._stock_quantity}\n状态: {status}"